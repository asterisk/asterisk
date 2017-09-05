"""add dtls_fingerprint to ps_endpoints

Revision ID: b83645976fdd
Revises: f3d1c5d38b56
Create Date: 2017-08-03 09:01:49.558111

"""

# revision identifiers, used by Alembic.
revision = 'b83645976fdd'
down_revision = 'f3d1c5d38b56'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

SHA_HASH_NAME = 'sha_hash_values'
SHA_HASH_VALUES = ['SHA-1', 'SHA-256']

def upgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'postgresql':
        enum = ENUM(*SHA_HASH_VALUES, name=SHA_HASH_NAME)
        enum.create(op.get_bind(), checkfirst=False)

    op.add_column('ps_endpoints',
             sa.Column('dtls_fingerprint', ENUM(*SHA_HASH_VALUES,
                 name=SHA_HASH_NAME, create_type=False)))

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_dtls_fingerprint_sha_hash_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'dtls_fingerprint')

    if context.bind.dialect.name == 'postgresql':
        enum = ENUM(*SHA_HASH_VALUES, name=SHA_HASH_NAME)
        enum.drop(op.get_bind(), checkfirst=False)
