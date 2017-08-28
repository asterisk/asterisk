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

SHA_HASH_VALUES = ['SHA-1', 'SHA-256']

def upgrade():
    op.add_column('ps_endpoints', sa.Column('dtls_fingerprint', sa.Enum(*SHA_HASH_VALUES, name='sha_hash_values')))

def downgrade():
    op.drop_column('ps_endpoints', 'dtls_fingerprint')
