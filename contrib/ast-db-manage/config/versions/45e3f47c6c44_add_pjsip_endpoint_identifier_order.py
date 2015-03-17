"""add pjsip endpoint_identifier_order

Revision ID: 45e3f47c6c44
Revises: 371a3bf4143e
Create Date: 2015-03-02 09:32:20.632015

"""

# revision identifiers, used by Alembic.
revision = '45e3f47c6c44'
down_revision = '371a3bf4143e'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('endpoint_identifier_order', sa.String(40)))

def downgrade():
    op.drop_column('ps_globals', 'endpoint_identifier_order')
