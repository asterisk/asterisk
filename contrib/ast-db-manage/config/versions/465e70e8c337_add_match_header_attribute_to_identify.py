"""Add match_header attribute to identify

Revision ID: 465e70e8c337
Revises: 28ab27a7826d
Create Date: 2017-03-14 08:13:53.986681

"""

# revision identifiers, used by Alembic.
revision = '465e70e8c337'
down_revision = '28ab27a7826d'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoint_id_ips', sa.Column('match_header', sa.String(255)))

def downgrade():
    op.drop_column('ps_endpoint_id_ips', 'match_header')
